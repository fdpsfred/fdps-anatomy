/* overview.h -- the battle map overview screen and its scaled map render.
 *
 * The overview is the whole-battlefield view the player opens during a
 * battle: the map is redrawn at a series of zoom factors into an offscreen
 * 320x200 8bpp page which is then presented to the mode 13h screen, so the
 * map appears to fly out and back.
 *
 * Surfaces here are 8bpp and addressed by a byte pointer with the visible
 * screen's own pitch of 0x140, the same convention blit.h uses; the page is
 * a full 320x200 = 0xfa00 bytes even though only a 0x138 x 0xc0 window of it
 * is filled with map.
 */
#ifndef OVERVIEW_H
#define OVERVIEW_H

/* Renders the battle map into dest, scaled, and clears everything the map
   does not cover.

   dest is a 320x200 8bpp page.  The whole 0xfa00 bytes are cleared to colour
   0 first, and then a 0x138 x 0xc0 window whose top-left corner is row 4,
   column 4 (dest + 0x504) is filled by walking the map.

   The walk is fixed point in units where one map tile is 0xc00 and one source
   pixel of a tile is 0x80, which is what makes a tile 24x24 pixels.  step is
   how far one output pixel advances in those units, so step 0x80 is 1:1 and a
   larger step zooms out; the caller sweeps it to animate the fly-out.

   world_x and world_y are the world position the window is CENTRED on, not
   its corner: the top-left is world_x - step * 0x9c and world_y - step * 0x60,
   half the window's width and height in output pixels.

   tile_table is the caller's array of pointers to 24x24 tile bitmaps, indexed
   tile_y * 0x40 + tile_x -- a fixed pitch of 64 entries per map row,
   independent of the map's real width, because that is the shape
   fdps_battle_map_overview builds.  The map's tile width and height come from
   the loaded terrain layer's own header instead, and a tile outside them is
   not drawn, so the cleared background shows through.

   A tile index off the left or right edge of the map still costs a table
   fetch: the pointer for the row's first tile is read before tile_x is
   checked, and so is the pointer after every wrap.  Only the pixel copy is
   guarded, so a negative tile_x reads tile_table one entry short of the row
   and does nothing with the pointer.  That is the original's behaviour and
   adding the missing guard would be a different program. */
extern void fdps_render_map_overview_scaled(unsigned char *dest, int world_x,
                                            int world_y, int step,
                                            unsigned char **tile_table);
#pragma aux fdps_render_map_overview_scaled "*" parm caller [];

#endif
