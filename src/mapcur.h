/* mapcur.h -- the battle map cursor: the overlay it paints into the scene
 * buffer, the information panel beside it, the select loop the player drives
 * it with and the animated move from one tile to another.
 *
 * The cursor's own state lives in the shared globals declared in gamedata.h:
 * its world-pixel position in data_fdps_map_cursor_world_x /
 * data_fdps_map_cursor_world_y, and which overlay it wears in
 * data_fdps_map_cursor_draw_mode.  Nothing in this file owns state of its own.
 */
#ifndef MAPCUR_H
#define MAPCUR_H

/* Paints the cursor overlay for the frame being composited, reading
   data_fdps_map_cursor_draw_mode to decide which of six shapes that is and
   the two cursor position globals to decide where.  Every shape is a
   hand-unrolled list of fdps_blit_cursor_tile calls, one per map tile, at the
   cursor position displaced by whole 24-pixel tiles; fdps_blit_cursor_tile
   drops the cells that fall outside the visible window, so no clipping
   happens here.

     mode 1  the bare cursor box, sprite 0.
     mode 2  the bare cursor box, sprite 1 -- the gapped style.
     mode 3  a radius-1 diamond.
     mode 4  a radius-2 diamond, all 13 tiles.
     mode 5  a radius-3 diamond, 21 tiles: the four tiles orthogonally
             adjacent to the centre are deliberately left blank.
     mode 6  draws NOTHING.  It clears the movement grid's marker byte for the
             tile under the cursor instead, which is how the tiles a cursor
             sweep passes over get marked.

   Mode 0 and any mode above 6 do nothing at all.  `scene_buffer` is the
   compositor's own 360-byte-pitch scene buffer and is passed straight through
   to fdps_blit_cursor_tile. */
extern void fdps_draw_map_cursor(unsigned char *scene_buffer);
#pragma aux fdps_draw_map_cursor "*" parm caller [];

#endif
