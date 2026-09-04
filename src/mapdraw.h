/* mapdraw.h -- composing the scrolling scene: the six parallax layer slots,
 * the order they are blitted in, and the map units drawn between them.
 *
 * A loaded chapter fills data_fdps_scene_layer_count of the six layer slots
 * (gamedata.h), each slot being one row across the parallel arrays there: a
 * tile map, a tileset, an attribute table, a scroll accumulator and step pair
 * per axis, a parallax factor per axis, and a depth byte.
 *
 * Drawing the scene is two passes over the same depth-sorted slot list with
 * the map units drawn in between, which is what makes the order matter: the
 * slots whose depth byte is below 10 go down first as background, the units
 * go on top of them, and the slots above 10 go over the units as foreground.
 */
#ifndef MAPDRAW_H
#define MAPDRAW_H

/* 00060154.  The palette index the marked and translucent tiles of a
   layer_mode 1 layer are tinted toward: fdps_draw_scene_layer copies it into
   slot 3 of the blend descriptor it hands fdps_blit_dispatch, and mode 0x0a
   weighs it against every source pixel.  A whole dword, taken as an index into
   a 256-entry shade-ramp row, and read nowhere else in the image.

   NOTHING IN THE IMAGE WRITES IT.  search_instructions over 0x00060154 finds
   exactly one access, the read at 0002c374, so this is a constant that lives
   in .data and never moves: the four bytes there are 0xff, palette index 255.
   Ticket 23 emits that initialiser; no code path can change it afterwards. */
extern int data_fdps_scene_marked_tile_tint_color;

/* 00060160 and 00060164.  The two-frame tile animation's state: which of the
   two frames is showing, and the tick the last flip happened on.

   The phase is a whole int although the flip is a byte operation -- XOR byte
   ptr [0x00060160],0x1 at 0002c39f -- because the read at 0002c52e is MOV
   EAX,[0x00060160], the 32-bit moffs form.  The byte-wide XOR is the
   peephole a bitwise operation on the low byte of an int gets, not evidence of
   a narrow variable, and narrowing the declaration to match it would change
   what the read sees once anything else stores a wide value.

   The latch is UNSIGNED and so is the compare against it: MOV EAX,[0x69d64] /
   SUB EAX,[0x00060164] / CMP EAX,0x3 / JBE at 0002c38f flips only when the
   difference is strictly more than three, with the unsigned jump, so a tick
   counter that has wrapped past the latch still yields a small difference
   rather than a large negative one.  A latch of 0 is treated as "never
   latched" and is seeded from the tick counter before the compare, which is
   what stops the very first frame of a session from flipping the phase.  That
   is not a corner the game only reaches after a reload: both dwords are zero
   in .data at 00060160 and 00060164, so the seeding branch runs on the first
   scene layer ever drawn.

   Both are read and written only by fdps_draw_scene_layer. */
extern int data_fdps_scene_tile_anim_phase;
extern unsigned int data_fdps_scene_tile_anim_last_flip_tick;

/* Fills draw_order with the layer slot indices 0, 1, ... sorted by ascending
   depth byte, and returns nothing: the list is the caller's array.

   draw_order must have room for data_fdps_scene_layer_count ints; nothing here
   checks it, and the only caller passes a six-element stack array to match the
   six-slot width of the layer table. */
extern void fdps_build_scene_layer_draw_order(int *draw_order);
#pragma aux fdps_build_scene_layer_draw_order "*" parm caller [];

/* Blits one layer's visible 14x9 window of 24x24 tiles into the scene buffer
   and returns nothing.

   scene_buf is the 360x240 8bpp page being composed; every blit goes into it
   at a fixed 0x168 pitch, inset 24 pixels from its top-left corner, and
   nothing else is written through it.  map_layer, tileset and tile_attr are
   this layer's row of data_fdps_scene_layer_tile_map_ptrs,
   data_fdps_scene_layer_tile_sheet_ptrs and
   data_fdps_scene_layer_tile_attr_ptr; move_grid is
   data_fdps_battle_move_grid_ptr, of which only byte 1 of each cell is read
   and only when layer_mode is non-zero.  scroll_x and scroll_y are the layer's
   view origin in pixels and may be negative.  layer_mode is the layer's byte
   of data_fdps_scene_layer_tile_attr_mode.

   NOTHING IS BOUNDS-CHECKED AND NOTHING IS CLIPPED.  The cell coordinates are
   wrapped modulo the map's own dimensions, so the window always names a cell
   that exists, but the tile id it finds there indexes the tileset's offset
   table and the attribute table's rows with no test of either; a map holding
   an id past the end of its sheet reads whatever follows it.  A map dimension
   of 0 divides by zero.

   IT MUTATES THREE GLOBALS EVERY CALL, and one of them per LAYER rather than
   per frame: data_fdps_marked_tile_blend_phase steps once here, so the
   movement-range highlight pulses faster on a map with more active layers.
   data_fdps_scene_tile_anim_phase and its latch are tick-gated and so move at
   most once per frame however many layers there are. */
extern void fdps_draw_scene_layer(unsigned char *scene_buf,
                                  unsigned char *map_layer,
                                  unsigned char *tileset,
                                  unsigned char *move_grid, int scroll_x,
                                  int scroll_y, unsigned char *tile_attr,
                                  int layer_mode);
#pragma aux fdps_draw_scene_layer "*" parm caller [];

#endif
