/* sprite.h -- the .SAF sprite drawers: one tilemap cell, one tilemap layer,
 * one composite sprite; and the two .CEL drawers, one for the Command.cel UI
 * sheet and one for a battle-map unit's walk sprite.
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

/* Draws one sprite of the global Command.cel sheet -- the sheet every piece of
   menu furniture in the game is cut from, 76 sprites of 25 by 22
   (resource_info/cel.md) -- into an 8bpp surface the caller owns.  Straight
   line, no test of any kind: one offset-table lookup and one call.

   `dst` is the destination byte itself, not a surface base: the caller has
   already advanced it to the sprite's top left pixel, typically as page base
   plus row times pitch plus column.  `pitch` is that surface's bytes per row,
   and the shipped callers pass three different ones -- 0x140 for a full 320
   wide page, 0x138 for the 312 wide shop and church list pages and 0x168 for
   the message window's own wider buffer -- so it is a real argument and not a
   constant waiting to be folded.  `sprite_index` selects the sheet entry.

   THE SHEET'S OWN HEADER IS NEVER CONSULTED.  Three numbers that the .CEL
   header records are written here as constants instead: the offset table's
   position, which the u16 at +0x05 states and which this hardwires at 0x0f,
   and the sprite width and height, which the i16 pair at +0x07 and +0x09
   states and which this hardwires at 25 and 22.  All three agree with what
   Command.cel actually declares, so the drawing is right; reading them from
   the header instead would be a different program that happens to behave the
   same on this one sheet, and would behave differently on any other.

   NOTHING IS RANGE CHECKED.  There is no compare against the sheet's sprite
   count and no lower bound, so an index of 76 reads the table's sentinel entry
   -- the file size -- and blits 550 bytes of whatever follows the sheet, and a
   negative index reads in front of the table.  The sheet pointer is not tested
   for null either: called before fdps_load_global_resources has filled the
   global, this dereferences a null pointer.

   Blit mode 0 is passed as a constant, so the sprite always goes through the
   opaque pass-through kernel: fill, stretch and literal runs are written to
   the destination byte for byte and only a skip run leaves the destination
   showing (rle.h).  The mode operand is 0 and is unused by that kernel. */
extern void fdps_blit_command_sprite(unsigned char *dst, int pitch,
                                     int sprite_index);
#pragma aux fdps_blit_command_sprite "*" parm caller [];

/* Draws one battle-map unit's 24 by 24 walk sprite into a scene buffer, in a
   blit mode the caller picks, so a unit can be painted normally or as a flat
   silhouette of one palette colour.  fdps_unit_rest, fdps_battle_advance_turn
   and fdps_flash_units_in_color are the three callers, and all three are
   flashing a unit inside their own wait loop.

   `scene_buffer` is the caller's 360 by 240 8bpp scene buffer (0x15180 bytes),
   already filled with the map; the sprite is composited into it and never onto
   the VGA page, and the 360-byte pitch is hardwired here rather than passed.
   `unit_index` is a position in the current battle's unit array and is not
   range checked -- it is handed straight to fdps_get_unit_record (unit.h), so
   the same re-resolution rule applies as there.  `blit_param` and `blit_mode`
   are fdps_blit_dispatch's mode operand and kernel selector, forwarded
   untouched; the shipped callers pass mode 3 with a palette colour shifted
   left by eight, which the recolour kernel reads as add 0x00, base = the
   colour, mask 0x00 and so paints every pixel that one index (rlecolor.h).

   THE UNIT IS DRAWN WHOLE OR NOT AT ALL.  The sprite origin alone is tested,
   strictly, against 0 < y < 0xd8 and 0 < x < 0x150 -- the scene buffer less
   one sprite on each axis -- and a unit that fails any of the four is dropped
   entirely rather than clipped.  Writing the lower bounds as >= 0 draws a row
   of units along the top edge and a column along the left that the original
   never shows (rebuild_info/pitfalls.md).

   THE UNIT IS DRAWN SNAPPED TO ITS TILE.  The record's sub-tile step counter
   is read and scaled by four and then never used, so a unit caught mid-step
   between two tiles appears on the tile it is leaving.  The sibling
   fdps_draw_map_unit does apply that counter through a facing switch; doing
   the same here moves flashed and resting units up to 20 pixels off
   (rebuild_info/pitfalls.md).

   The walk frame is chosen from the shared map animation counter, which
   fdps_draw_map_unit is the only writer of, divided by four with 3 folded back
   to 1, giving the ping-pong 0, 1, 2, 1.  Which sprite that selects is the
   record's cache slot times twelve plus its facing times three plus that
   phase, indexed into the offset table at the base of
   data_fdps_cel_sprite_cache_ptr; nothing bounds the index and the cache
   pointer is not tested for null, so a call before
   fdps_cache_cel_sprite_group has run reads through a null base. */
extern void fdps_blit_unit_sprite(unsigned char *scene_buffer, int unit_index,
                                  unsigned int blit_param, int blit_mode);
#pragma aux fdps_blit_unit_sprite "*" parm caller [];

#endif
