/* movegrid.h -- the battle map's working movement grid: resetting it, marking
 * zones of control on it, flooding a movement range over it and tracing a path
 * back out of it.
 *
 * The grid is one heap block reached through data_fdps_battle_move_grid_ptr.
 * Its first four bytes are a header -- signed 16-bit tile width at +0, signed
 * 16-bit tile height at +2 -- and the width*height cells follow at +4 in
 * row-major order, two bytes each: cell (x, y) is at base + 4 + 2 * (y * width
 * + x).  A cell is struct fdps_move_grid_cell in src/fdpstype.h.
 *
 * Byte 0 of a cell is the zone-of-control flag byte: 0x40 means a unit stands
 * on this tile and it may not be entered, 0x80 means the tile adjoins a unit
 * so movement entering it must stop.  Byte 1 is the flood fill's marker, which
 * the fill relaxes downwards from the 0xff unreachable sentinel.
 *
 * fdps_field_load_chapter_resources allocates the block as 4 + width*height*2
 * bytes and writes the header; everything in this file works on what that
 * pointer holds and owns no state of its own.
 */
#ifndef MOVEGRID_H
#define MOVEGRID_H

/* Blanks every cell of the grid so a new movement range or target mask can be
   computed over it: clears the two zone-of-control bits of byte 0, preserving
   the low six bits, and stores the 0xff unreachable sentinel into byte 1.
   Does nothing at all when the grid has not been allocated. */
extern void fdps_map_grid_reset(void);
#pragma aux fdps_map_grid_reset "*" parm caller [];

/* Marks one unit's zone of control into the grid: ORs 0x80 into each of the
   four orthogonal neighbours of (tile_x, tile_y) that exists, then ORs 0x40
   into the tile itself.  Both bits are sticky -- only fdps_map_grid_reset
   clears them -- so calling this once per unit accumulates every unit's zone
   into the one grid.  Does nothing at all when the grid has not been
   allocated. */
extern void fdps_move_grid_mark_zone_of_control(int tile_x, int tile_y);
#pragma aux fdps_move_grid_mark_zone_of_control "*" parm caller [];

#endif
