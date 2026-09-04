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

/* 00069d18.  The frame tick the map's walk-animation clock was last stepped
   on.  fdps_draw_map_unit compares data_fdps_timer_tick_counter against it and
   steps the two counters below only when the two differ, which is what holds
   the clock to one step per frame even though fdps_draw_map_units calls that
   routine twice for every unit on the map.

   Read and written only by fdps_draw_map_unit, and only as a whole dword: the
   compare at 0002cdd1 is CMP EAX,dword ptr [0x00069d18].  Unsigned, to match
   the tick counter it latches; the only operation on the pair is equality, so
   a counter that has wrapped is compared correctly either way. */
extern unsigned int data_fdps_map_unit_anim_last_tick;

/* 00069d1c and 00060168.  The status-icon rotation: a counter that steps once
   per frame tick and wraps at 0x19, and the cycle number that is bumped every
   time it wraps to 0.  The cycle is what fdps_draw_map_unit hands
   fdps_unit_select_status_icon as its rotation argument, so a unit carrying
   more than one ailment shows each of their icons in turn, one every 25 ticks.

   Both are signed ints: the wrap at 0002ce0f is IDIV, and the compare against
   zero at 0002ce17 is on the whole dword.  Both are stepped only by
   fdps_draw_map_unit; the cycle is otherwise read only there too. */
extern int data_fdps_map_unit_status_icon_tick_counter;
extern int data_fdps_map_unit_status_icon_cycle;

/* 0006014c.  Which of fdps_draw_map_units' two passes over the unit list is
   running: non-zero while it is laying down shadows, zero while it is drawing
   the sprites themselves.  fdps_draw_map_units sets it to 1 at 0002d24c and
   back to 0 at 0002d287, and fdps_draw_map_unit is the only reader.

   A byte, tested CMP byte ptr [0x0006014c],0x0 at 0002cf96. */
extern unsigned char data_fdps_map_unit_shadow_pass_flag;

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

/* Draws one map unit into the scene buffer -- its shadow or its sprite,
   whichever pass data_fdps_map_unit_shadow_pass_flag says is running -- and
   steps the map's shared walk-animation clock on the way past.  Returns
   nothing.

   THE WALK CLOCK LIVES IN HERE.  data_fdps_map_unit_walk_anim_counter,
   data_fdps_map_unit_status_icon_tick_counter and
   data_fdps_map_unit_status_icon_cycle are stepped by this function and by
   nothing else in the image, and only on a frame tick the latch above has not
   already seen.  Stepping them at the top of the frame loop instead, or
   dropping the latch, runs the walk cycle at twice the unit count per frame or
   stops it on the frames where only the ring menu draws
   (rebuild_info/pitfalls.md).

   unit_index selects the 0x50-byte record in the array at
   data_fdps_map_unit_array_ptr and is not range checked.  scene_buf is the
   destination surface, always at pitch 0x168 with a 24-pixel border on both
   axes.  unused_flag is overwritten with 0 at 0002cdba before any read, so no
   value passed there can be observed; all three call sites still push one.

   Nothing is drawn for a record whose portrait id is 0x80, for a retired unit
   -- bit 0 of the flag byte -- or for a unit outside the camera window.  The
   two passes clip against vertical windows two pixels apart, because the
   shadow is laid down two scanlines below the cell; folding them into one
   shared test makes shadows appear and disappear a row early or late along the
   top and bottom edges (rebuild_info/pitfalls.md). */
extern void fdps_draw_map_unit(int unit_index, unsigned char *scene_buf,
                               unsigned char unused_flag);
#pragma aux fdps_draw_map_unit "*" parm caller [];

/* Draws every unit on the map into the scene buffer: one whole sweep laying
   down every shadow, then a second whole sweep drawing every sprite.  Returns
   nothing.

   THE SWEEPS ARE SEPARATE ON PURPOSE and both walk 0 up to
   data_fdps_map_unit_count.  Drawing each unit's shadow and sprite together in
   one loop is the same arithmetic and a different picture: a unit later in the
   table lays its translucent shadow over a unit already drawn, which the
   original never does (rebuild_info/pitfalls.md).

   scene_buf is the destination surface, pitch 0x168, forwarded unchanged.
   unused_flag is forwarded as fdps_draw_map_unit's third argument, which that
   routine overwrites with 0 before any read, so nothing passed here has any
   effect; the only caller, fdps_draw_scene_layers, passes 0.

   data_fdps_map_unit_shadow_pass_flag is set to 1 for the first sweep and back
   to 0 for the second, and is left at 0 on the way out.  This is its only
   writer. */
extern void fdps_draw_map_units(unsigned char *scene_buf,
                                unsigned char unused_flag);
#pragma aux fdps_draw_map_units "*" parm caller [];

#endif
