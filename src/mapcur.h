/* mapcur.h -- the battle map cursor: the overlay it paints into the scene
 * buffer, the information panel beside it, the select loop the player drives
 * it with and the animated move from one tile to another.
 *
 * The cursor's own state lives in the shared globals declared in gamedata.h:
 * its world-pixel position in data_fdps_map_cursor_world_x /
 * data_fdps_map_cursor_world_y, and which overlay it wears in
 * data_fdps_map_cursor_draw_mode.  The one piece of state this file owns is
 * which side of the screen the information panel is parked on, below.
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

/* 0006016c.  Which screen column the terrain information panel is drawn at,
   and the only global this file owns.  All eight references to it in the image
   are inside fdps_draw_cursor_info_panel: the two stores that park it and the
   six MOVSX reads that place the panel and each figure inside it.

   It is STICKY, and that is the whole of its reason for being a global rather
   than a local: the panel only moves when the cursor is low on the screen and
   at one end of it, and it stays wherever it was last put for every frame in
   between.  Nothing else initialises it, so the value the image's data segment
   carries -- 0x19 -- is where the panel starts.

   A signed short: every read is MOVSX word ptr, and the value is added to a
   frame buffer pointer as an int. */
extern short data_fdps_ui_terrain_hud_panel_offset;

/* Draws the terrain information panel over the composited scene: what the
   ground under the cursor does to a fighter, and who is standing on it.
   `scene_buffer` is fdps_render_view_frame's own 360 by 240 offscreen buffer,
   the same one fdps_draw_scene_layers has just filled, and every figure and
   sprite goes into it at a 360-byte pitch.

   IT DRAWS NOTHING UNLESS BOTH FLAGS ARE SET.  data_fdps_ui_terrain_hud_user_-
   enabled is the player's own toggle out of the options menu and
   data_fdps_ui_play_active_flag says the map is the live surface rather than a
   menu or a cut scene (both in gamedata.h); with either clear the function
   returns having drawn nothing and having left the panel column alone.

   It is not a pure drawer.  Before it draws it calls fdps_map_load_tile_info
   (maptile.h) for the cursor's tile, which republishes the whole tile-info
   scratch block -- the tile id, the terrain class, the attribute flags, the
   combat backdrop, the movement marker and the cell event code -- so a caller
   that renders a frame also refreshes that block as a side effect.  It also
   parks data_fdps_number_glyph_color_row back at 0 on the way out of the unit
   arm, whatever it held on the way in.

   What it puts on the screen, all of it placed relative to the panel column
   above: the panel background, the cursor tile's own graphic, the attack and
   defense percentages the terrain class gives, and, when a unit is standing on
   the tile, that unit's walking sprite over the tile graphic and its current
   HP under it.  A unit that is not at full HP has its HP figure drawn in
   colour row 3 rather than the ambient one.

   The two percentages are drawn in whatever colour row the previous drawing
   call left behind -- this function does not set one for them -- which is the
   contract data_fdps_number_glyph_color_row's declaration describes. */
extern void fdps_draw_cursor_info_panel(unsigned char *scene_buffer);
#pragma aux fdps_draw_cursor_info_panel "*" parm caller [];

#endif
