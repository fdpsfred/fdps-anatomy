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

#include "fdpstype.h"

/* 00063c50 and 00063930.  The flood fill's frontier: the tile columns and the
   tile rows of the cells one wave of fdps_move_grid_flood_fill_range has to
   expand from.  Each is a pair of 400-entry queues laid end to end -- buffer 0
   at index 0 and buffer 1 at index 400 -- and the fill alternates between them,
   reading the wave it is on out of one while appending the next wave into the
   other.  A one-byte entry, so a tile index above 255 could not be queued; no
   map in the game is that wide.

   Each is ONE array of 800 and not two of 400 (rebuild_info/pitfalls.md,
   contract B): the fill forms every index as buffer * 400 + slot, and a wave
   longer than 400 entries runs off the end of one buffer into the other, which
   only stays inside the object while the two halves are one declaration.  The
   two arrays are adjacent in bss -- 00063930 + 800 is 00063c50 -- and nothing
   indexes from one into the other, so they stay two globals.

   Nothing outside src/movegrid.c reads either of them: they are scratch for
   the one function, not a result anybody collects.  The definitions arrive
   with ticket 23 like every other data_fdps_ global. */
extern unsigned char data_fdps_battle_move_frontier_x[800];
extern unsigned char data_fdps_battle_move_frontier_y[800];

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

/* ORs the movement-stops-here bit 0x80 into the single cell (tile_x, tile_y),
   leaving every other bit of the cell alone.  A tile carrying it may be
   entered but movement ends on it; only fdps_map_grid_reset clears it again.
   Neither coordinate is bounds-checked and the grid pointer is not checked for
   null: the cell is addressed and written unconditionally.  Nothing in the
   original image calls this. */
extern void fdps_move_grid_set_stop_flag(int tile_x, int tile_y);
#pragma aux fdps_move_grid_set_stop_flag "*" parm caller [];

/* Marks the zone of control of every unit on the side opposite the caller's
   into the grid, so the movement range flooded over it afterwards cannot be
   walked through them.  side_select is a TRUTH VALUE, not a side number: 0
   marks every unit whose side byte is non-zero, any non-zero value marks every
   unit whose side byte is 0.  Retired units (flags bit 0) are passed over.
   Callers reset the grid first, and a caller that wants the whole map's zones
   calls this twice, with 0 and then 1. */
extern void fdps_move_grid_mark_opposing_zones_of_control(int side_select);
#pragma aux fdps_move_grid_mark_opposing_zones_of_control "*" parm caller [];

/* Floods one unit's movement range over the grid: starting from the tile the
   unit stands on, relaxes the accumulated movement cost stored in byte 1 of
   every cell the unit can still afford to enter, so
   fdps_map_grid_collect_marked_tiles below reads out the range and
   fdps_move_path_trace walks the same bytes to build a path.

   class_move_cost is the acting unit's class record and only its move_cost[8]
   is touched: the cost of entering a tile is move_cost[terrain type of that
   tile].  move_points is the allowance, and a tile stays reachable while its
   accumulated cost is <= it.  Callers that want the whole map covered rather
   than one unit's real allowance pass 0x64.

   The two zone-of-control bits the marks above leave in byte 0 are read here:
   0x40 refuses the cell outright, 0x80 stores move_points into it instead of
   the cost that was computed.  Expects fdps_map_grid_reset to have run first,
   so every cell holds the 0xff sentinel a candidate cost can undercut.

   Neither the grid pointer nor the two scene layer pointers are checked, and
   neither start coordinate is range checked. */
extern void fdps_move_grid_flood_fill_range(
                struct fdps_class_record *class_move_cost,
                int start_x, int start_y, int move_points);
#pragma aux fdps_move_grid_flood_fill_range "*" parm caller [];

/* Takes the tiles that occupied units stand on back out of the movement range
   that has just been flooded over the grid, by storing the 0xff unreachable
   sentinel into byte 1 of each of their cells, so the moving unit may cross
   them but may not end its move on one.  The unit at exclude_unit_index keeps
   its own tile, so standing still stays legal, and retired units (flags bit 0)
   are passed over.  side_select is a TRUTH VALUE, not a side number: 0 blocks
   the units whose side byte is 0, any non-zero value blocks the units whose
   side byte is non-zero -- the opposite polarity to the identically named
   argument of fdps_move_grid_mark_opposing_zones_of_control above.  Neither
   the unit coordinates nor the grid pointer are checked. */
extern void fdps_move_grid_block_occupied_tiles(int exclude_unit_index,
                                                int side_select);
#pragma aux fdps_move_grid_block_occupied_tiles "*" parm caller [];

/* Reads the grid out into a flat list: writes one (x, y) pair of tile indices,
   x first, into out_coords for every cell whose marker byte is not the 0xff
   unreachable sentinel, and returns how many pairs that was.  The cells are
   visited in row-major order, so the pairs come out row by row.  The grid is
   not modified and the grid pointer is not checked for null.  Nothing bounds
   how many pairs are written: the caller's buffer has to be big enough for
   every cell of the map, and two of the callers in the original image hand
   over one that is not. */
extern int fdps_map_grid_collect_marked_tiles(unsigned char *out_coords);
#pragma aux fdps_map_grid_collect_marked_tiles "*" parm caller [];

#endif
